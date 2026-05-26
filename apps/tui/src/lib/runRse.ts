import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export type RunRseResult = {
	kind: 'system' | 'hint' | 'error';
	text: string;
};

const HELP_LINES: ReadonlyArray<RunRseResult> = [
	{ kind: 'hint', text: 'Type commands with or without the leading rse.' },
	{ kind: 'hint', text: 'Backend commands:' },
	{ kind: 'system', text: '  version | --version | -v' },
	{ kind: 'system', text: '  doctor | doc' },
	{ kind: 'system', text: '  config show' },
	{ kind: 'system', text: '  info --in <image> [--msg-block <text>] [--key <key>] [--step <float>] [--layout center-ring|keyed-shuffle]' },
	{ kind: 'system', text: '  embed --in <image> --out <image> --msg-block <text> [--key <key>] [--step <float>] [--layout center-ring|keyed-shuffle]' },
	{ kind: 'system', text: '  extract --in <image> [--key <key>] [--step <float>] [--layout center-ring|keyed-shuffle|auto]' },
	{ kind: 'system', text: '  verify --in <image> [--key <key>] [--step <float>] [--layout center-ring|keyed-shuffle|auto]' },
	{ kind: 'hint', text: 'TUI-local command:' },
	{ kind: 'system', text: '  clear' },
	{ kind: 'hint', text: 'Examples:' },
	{ kind: 'system', text: '  info --in ./tests/fixtures/input/input.jpg' },
	{ kind: 'system', text: '  embed --in ./tests/fixtures/input/input.jpg --out ./rse.png --msg-block hello' },
	{ kind: 'system', text: '  extract --in ./rse.png' },
	{ kind: 'system', text: '  verify --in ./rse.png' },
] as const;

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const REPO_ROOT = path.resolve(__dirname, '../../../..');
const LOCAL_RSE_CANDIDATES = [
	path.join(REPO_ROOT, 'build', 'rse'),
	path.join(REPO_ROOT, 'cmake-build-debug', 'rse'),
] as const;

function pushOutputLine(
	output: Array<{ kind: 'stdout' | 'stderr'; text: string }>,
	kind: 'stdout' | 'stderr',
	line: string
) {
	const normalizedLine = line.trimEnd();
	if (normalizedLine.length === 0) {
		return;
	}

	output.push({
		kind,
		text: normalizedLine,
	});
}

function appendChunkToBuffer(
	output: Array<{ kind: 'stdout' | 'stderr'; text: string }>,
	buffer: string,
	chunk: Buffer | string,
	kind: 'stdout' | 'stderr'
) {
	const nextBuffer = `${buffer}${chunk.toString()}`;
	const lines = nextBuffer.split('\n');

	for (let index = 0; index < lines.length - 1; index += 1) {
		const normalizedLine = lines[index]?.replace(/\r$/, '');
		if (normalizedLine !== undefined) {
			pushOutputLine(output, kind, normalizedLine);
		}
	}

	return lines[lines.length - 1] ?? '';
}

function splitCommand(command: string): string[] {
	const args: string[] = [];
	let current = '';
	let quote: '"' | "'" | null = null;
	let escaping = false;

	for (const char of command.trim()) {
		if (escaping) {
			current += char;
			escaping = false;
			continue;
		}

		if (char === '\\') {
			escaping = true;
			continue;
		}

		if (quote !== null) {
			if (char === quote) {
				quote = null;
			} else {
				current += char;
			}
			continue;
		}

		if (char === '"' || char === "'") {
			quote = char;
			continue;
		}

		if (/\s/.test(char)) {
			if (current.length > 0) {
				args.push(current);
				current = '';
			}
			continue;
		}

		current += char;
	}

	if (escaping) {
		current += '\\';
	}

	if (quote !== null) {
		throw new Error('Unterminated quoted string.');
	}

	if (current.length > 0) {
		args.push(current);
	}

	return args;
}

export async function runRse(command: string): Promise<RunRseResult[]> {
	const normalizedCommand = command.trim();

	if (normalizedCommand === 'help') {
		return [...HELP_LINES];
	}

	let args: string[];
	try {
		args = splitCommand(normalizedCommand);
	} catch (error) {
		const message = error instanceof Error ? error.message : 'Failed to parse command.';
		return [{ kind: 'error', text: message }];
	}

	if (args.length === 0) {
		return [];
	}

	if (args[0] === 'rse') {
		args.shift();
	}

	const envPath = process.env.RSE_BIN;

	let resolvedPath: string | null = null;

	if (envPath && fs.existsSync(envPath)) {
		resolvedPath = envPath;
	} else {
		for (const candidate of LOCAL_RSE_CANDIDATES) {
			if (fs.existsSync(candidate)) {
				resolvedPath = candidate;
				break;
			}
		}
	}

	return new Promise<RunRseResult[]>((resolve) => {
		const child = resolvedPath
			? spawn(resolvedPath, args, { stdio: ['ignore', 'pipe', 'pipe'] })
			: spawn('rse', args, { stdio: ['ignore', 'pipe', 'pipe'] });

		const output: { kind: 'stdout' | 'stderr'; text: string }[] = [];
		let stdoutBuffer = '';
		let stderrBuffer = '';

		child.stdout.on('data', (chunk: Buffer | string) => {
			stdoutBuffer = appendChunkToBuffer(output, stdoutBuffer, chunk, 'stdout');
		});

		child.stderr.on('data', (chunk: Buffer | string) => {
			stderrBuffer = appendChunkToBuffer(output, stderrBuffer, chunk, 'stderr');
		});

		child.on('error', (error: Error & { code?: string }) => {
			if (error.code === 'ENOENT') {
				resolve([
					{ kind: 'error', text: 'Error: rse binary not found.' },
					{ kind: 'hint', text: 'Build it with:' },
					{ kind: 'hint', text: 'cmake -S . -B build -DRoadscriptEngine_DIR=/path/to/roadscript-engine/install/lib/cmake/RoadscriptEngine' },
					{ kind: 'hint', text: 'cmake --build build --target rse' },
					{ kind: 'hint', text: 'Or set:' },
					{ kind: 'hint', text: 'export RSE_BIN=/path/to/rse' },
				]);
				return;
			}

			resolve([
				{ kind: 'error', text: `[rse] failed to start: ${error.message}` },
			]);
		});

		child.on('close', (code: number | null) => {
			pushOutputLine(output, 'stdout', stdoutBuffer.replace(/\r$/, ''));
			pushOutputLine(output, 'stderr', stderrBuffer.replace(/\r$/, ''));

			const entries: RunRseResult[] = output.map((entry) => ({
				kind: entry.kind === 'stdout' ? 'system' : 'error',
				text: entry.text,
			}));

			if (code !== 0) {
				entries.push({
					kind: 'error',
					text: `[rse] exited with code ${code ?? 'null'}`,
				});
			}

			resolve(entries);
		});
	});
}
