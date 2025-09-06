import time
import json
import urllib.request
import datetime
import subprocess
import logging
import logging.handlers

NOAA_URL = 'https://www.ndbc.noaa.gov/data/5day2/{buoy_id}_5day.txt'
NOAA_TIMEOUT = 15

logger = logging.getLogger(__name__)


class Observation:
    def __init__(self, time, wave_height, period):
        self.time = time
        self.wave_height = wave_height
        self.period = period

    def truncate_time(self):
        return self.time.replace(minute=0, second=0, microsecond=0)

    def copy_at_time(self, time):
        return Observation(time, self.wave_height, self.period)

    def __repr__(self):
        t = self.time.strftime('%Y-%m-%dT%H:%M')
        return 'time={}, wave_height={:.1f}, period={:.0f}'.format(t, self.wave_height, self.period)


class Observations:
    def __init__(self, observations):
        self.observations = observations

    def has_latest(self):
        return len(self.observations) > 0

    def latest(self):
        if self.observations:
            return self.observations[0]

    def latest_time(self):
        if self.observations:
            return self.observations[0].time

    def wave_height_up(self):
        if len(self.observations) >= 2:
            return self.observations[0].wave_height >= self.observations[1].wave_height
        return True

    def newer_than(self, observations):
        my_time = self.latest_time()
        other_time = observations.latest_time()
        return my_time is not None and (other_time is None or my_time > other_time)

    def one_per_hour(self, limit=32):
        if not self.has_latest():
            return None
        result = [self.observations[0]]
        obs_index = 1
        while len(result) < limit:
            prev_tt = result[-1].truncate_time()
            next_tt = prev_tt - datetime.timedelta(hours=1)
            if obs_index == len(self.observations):
                result.append(result[-1].copy_at_time(next_tt))
                continue
            obs = self.observations[obs_index]
            obs_tt = obs.truncate_time()
            if obs_tt == next_tt:
                result.append(obs)
                obs_index += 1
            elif obs_tt < next_tt:
                result.append(result[-1].copy_at_time(next_tt)) # filler
            else:
                obs_index += 1 # duplicate
        return reversed(result)


class Process:
    def __init__(self, command):
        self.command = command
        self.proc = None

    def start(self):
        self.proc = subprocess.Popen(self.command)
        logger.info('started process, id={}'.format(self.proc.pid))

    def stop(self):
        self.proc.send_signal(2)  # SIGINT
        try:
            self.proc.wait(5)
            logger.info('interrupted process, id={}, status={}'.format(self.proc.pid, self.proc.returncode))
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            self.proc.wait()
            logger.info('terminated  process, id={}, status={}'.format(self.proc.pid, self.proc.returncode))

    def restart(self):
        if self.proc:
            self.stop()
        self.start()

    def check(self):
        self.proc.poll()
        if self.proc.returncode is None:
            logger.info('process is running, id={}'.format(self.proc.pid))
        else:
            logger.error('process died, id={}, status={}'.format(self.proc.pid, self.proc.returncode))
            self.proc = None

    def dead(self):
        return self.proc is None


def init_logger(file_name):
    formatter = logging.Formatter('[%(asctime)s] <%(threadName)s> %(levelname)s - %(message)s')

    handler = logging.handlers.RotatingFileHandler(file_name, maxBytes=100000, backupCount=3)
    handler.setFormatter(formatter)

    log = logging.getLogger('')
    log.setLevel(logging.INFO)
    log.addHandler(handler)


def fetch_buoy_data_last5(buoy_id):
    url = NOAA_URL.format(buoy_id=buoy_id)
    return urllib.request.urlopen(url, None, NOAA_TIMEOUT).read().decode('utf-8')


def column_index_of(headers, target):
    for n in range(len(headers)):
        if headers[n] == target:
            return n


def parse_value(values, index, fn, range):
    try:
        n = fn(values[index])
        return n if range[0] <= n <= range[1] else None
    except:
        return None


def parse_int(values, index, range):
    return parse_value(values, index, int, range)


def parse_float(values, index, range):
    return parse_value(values, index, float, range)


def meters_to_feet(meters):
    return meters * 3.28084


def parse_observations(csv):
    lines = iter(csv.splitlines())

    header_line = next(lines)

    # skip units header line
    next(lines)

    headers = header_line.split()

    index_year = column_index_of(headers, '#YY')
    index_month = column_index_of(headers, 'MM')
    index_day = column_index_of(headers, 'DD')
    index_hour = column_index_of(headers, 'hh')
    index_minute = column_index_of(headers, 'mm')
    index_wave_height = column_index_of(headers, 'WVHT')
    index_dom_period = column_index_of(headers, 'DPD')

    observations = []

    for line in lines:
        words = line.split()
        year = parse_int(words, index_year, (1970, 2500))
        month = parse_int(words, index_month, (1, 12))
        day = parse_int(words, index_day, (1, 31))
        hour = parse_int(words, index_hour, (0, 23))
        minute = parse_int(words, index_minute, (0, 59))
        wave_height = parse_float(words, index_wave_height, (0, 98))
        dom_period = parse_float(words, index_dom_period, (0, 98))

        # abort incomplete rows
        if any(True if v is None else False for v in (year, month, day, hour, minute, wave_height, dom_period)):
            continue

        date = datetime.datetime(year, month, day, hour, minute, tzinfo=datetime.timezone.utc).astimezone(tz=None)

        observations.append(Observation(date, meters_to_feet(wave_height), dom_period))

    return Observations(observations)


def update_observations(conf):
    updated = False
    for station in conf['stations']:
        try:
            obs = parse_observations(fetch_buoy_data_last5(station['id']))
            logger.info('fetched observations for station {}, latest is {}'.format(station['id'], obs.latest()))
            prev = station.get('observations')
            if not prev or obs.newer_than(prev):
                station['observations'] = obs
                updated = True
        except:
            logger.exception('error occurred fetching observations for station {}'.format(station['id']))
    return updated


def update_message_file(conf):
    targets = [s for s in conf['stations'] if 'observations' in s and s['observations'].has_latest()]
    lines = [f'{len(targets)}']
    for station in targets:
        obss = station.get('observations')
        obs = obss.latest()
        dt = f'{obs.time.month}/{obs.time.day} at {obs.time.strftime("%I:%M %p").lstrip("0")}'
        lines.append('+' if obss.wave_height_up() else '-')
        lines.append(f'{station["name"]} {obs.wave_height:.1f}\' @ {round(obs.period)}s on {dt}')
        for obs in obss.one_per_hour():
            lines.append(f'{round(obs.wave_height)}')
    with open('buoys.txt', 'w') as f:
        f.write('\n'.join(lines))
    logger.info('updated buoys.txt')


def start_proc_if_dead(proc):
    proc.poll()
    if proc.returncode is None:
        logger.info('process still running, id={}'.format(proc.pid))
    else:
        logger.error('process died, id={}, status={}'.format(proc.pid, proc.returncode))


def main():
    init_logger('buoys.log')

    with open('buoys.json', 'r') as f:
        conf = json.load(f)

    booted = False
    proc = Process(conf['command'])
    while True:
        updated = update_observations(conf)
        if updated:
            booted = True
            update_message_file(conf)
            proc.restart()
        if booted:
            proc.check()
            if proc.dead():
                proc.start()
        time.sleep(60)


if __name__ == '__main__':
    main()
