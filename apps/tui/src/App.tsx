import React, { useRef, useState } from 'react';
import { Box, Text, useWindowSize } from 'ink';

import CommandInput from './components/CommandInput.js';
import OutputLog, { type OutputLogEntry } from './components/OutputLog.js';
import { runRse } from './lib/runRse.js';

type AppStatus = 'Ready' | 'Running' | 'Done' | 'Error';

const LARGE_BANNER = [
	'██████╗ ███████╗███████╗███╗   ██╗ ██████╗ ██╗███╗   ██╗███████╗',
	'██╔══██╗██╔════╝██╔════╝████╗  ██║██╔════╝ ██║████╗  ██║██╔════╝',
	'██████╔╝███████╗█████╗  ██╔██╗ ██║██║  ███╗██║██╔██╗ ██║█████╗  ',
	'██╔══██╗╚════██║██╔══╝  ██║╚██╗██║██║   ██║██║██║╚██╗██║██╔══╝  ',
	'██║  ██║███████║███████╗██║ ╚████║╚██████╔╝██║██║ ╚████║███████╗',
	'╚═╝  ╚═╝╚══════╝╚══════╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝╚══════╝',
] as const;

const WELCOME_TIPS = [
	'Run `help` to see the native backend commands and examples.',
	'Type commands with or without the leading `rse`.',
	'Use Up and Down to recall command history.',
] as const;

const SPACING = {
	XS: 0,
	S: 1,
	M: 2,
	L: 3,
} as const;

const TOP_PADDING_ROWS = 1;
const LARGE_BANNER_MIN_COLUMNS = 120;
const LARGE_BANNER_MIN_ROWS = 32;

function statusColor(status: AppStatus) {
	if (status === 'Error') {
		return 'red';
	}

	if (status === 'Running') {
		return 'yellow';
	}

	if (status === 'Done') {
		return 'green';
	}

	return 'cyan';
}

function statusLabel(status: AppStatus) {
	return status === 'Running' ? 'Running...' : status;
}

function clampToMinimum(value: number, minimum: number) {
	return value < minimum ? minimum : value;
}

function buildQuickStartLines(availableRows: number) {
	if (availableRows <= 0) {
		return [];
	}

	if (availableRows === 1) {
		return ['Run `help` to see available commands.'];
	}

	if (availableRows <= 3) {
		const tipCount = Math.max(0, Math.min(WELCOME_TIPS.length, availableRows - 1));
		return ['Quick start', ...WELCOME_TIPS.slice(0, tipCount)];
	}

	const contentRows = availableRows - SPACING.M;
	const tipCount = Math.max(0, Math.min(WELCOME_TIPS.length, contentRows - 1));
	return [' ', 'Quick start', ...WELCOME_TIPS.slice(0, tipCount), ' '];
}

function buildSessionLabel(columns: number, visibleEntryCount: number, clippedEntryCount: number) {
	if (clippedEntryCount <= 0) {
		return 'Session';
	}

	if (columns < 72) {
		return `Latest ${visibleEntryCount} entries`;
	}

	return `Session  ·  Showing latest ${visibleEntryCount} entries`;
}

export default function App() {
	const nextIdRef = useRef(0);
	const [entries, setEntries] = useState<OutputLogEntry[]>([]);
	const [status, setStatus] = useState<AppStatus>('Ready');
	const [isRunning, setIsRunning] = useState(false);
	const { columns, rows } = useWindowSize();
	const hasEntries = entries.length > 0;
	const terminalIsTight = rows < LARGE_BANNER_MIN_ROWS || columns < LARGE_BANNER_MIN_COLUMNS;
	const showLargeBanner = !terminalIsTight;
	const shellPlaceholder = columns < 72
		? 'help, version, embed...'
		: 'help, version, doctor, info, embed, extract, verify';
	const headerHeight = showLargeBanner ? LARGE_BANNER.length + 1 : 2;
	const inputHeight = 2;
	const footerHeight = 1;
	const gapBelowHeader = SPACING.S;
	const gapAboveInput = SPACING.S;
	const gapAboveFooter = SPACING.S;
	const reservedHeight =
		TOP_PADDING_ROWS +
		headerHeight +
		gapBelowHeader +
		gapAboveInput +
		inputHeight +
		gapAboveFooter +
		footerHeight;
	const middleHeight = clampToMinimum(rows - reservedHeight, 2);
	const outputLeadGap = hasEntries ? SPACING.S : SPACING.XS;
	const visibleEntryCount = clampToMinimum(middleHeight - 1 - outputLeadGap, 1);
	const visibleEntries = entries.slice(-visibleEntryCount);
	const clippedEntryCount = Math.max(entries.length - visibleEntries.length, 0);
	const sessionLabel = buildSessionLabel(columns, visibleEntries.length, clippedEntryCount);
	const quickStartLines = hasEntries ? [] : buildQuickStartLines(middleHeight);
	const showQuickStart = quickStartLines.length > 0;

	const makeEntry = (kind: OutputLogEntry['kind'], text: string): OutputLogEntry => ({
		id: `entry-${nextIdRef.current++}`,
		kind,
		text,
	});

	const handleSubmit = async (command: string) => {
		if (isRunning) {
			return;
		}

		if (command === 'clear') {
			setEntries([]);
			setStatus('Ready');
			return;
		}

		const commandEntry = makeEntry('command', `➜  ~ ${command}`);
		setEntries((currentEntries) => [...currentEntries, commandEntry]);
		setIsRunning(true);
		setStatus('Running');

		try {
			const responseEntries = (await runRse(command)).map((entry) => makeEntry(entry.kind, entry.text));
			const hasError = responseEntries.some((entry) => entry.kind === 'error');

			setEntries((currentEntries) => [...currentEntries, ...responseEntries]);
			setStatus(hasError ? 'Error' : 'Done');
		} finally {
			setIsRunning(false);
		}
	};

	return (
		<Box
			flexDirection="column"
			height={rows}
			paddingX={columns >= 120 ? 3 : 1}
		>
			<Box height={TOP_PADDING_ROWS}>
				<Text> </Text>
			</Box>

			<Box flexDirection="column" height={headerHeight}>
				{showLargeBanner ? (
					<>
						{LARGE_BANNER.map((line) => (
							<Text key={line} color="cyan">
								{line}
							</Text>
						))}
						<Text color="gray">Interactive watermark terminal  ·  {statusLabel(status)}</Text>
					</>
				) : (
					<>
						<Text bold color="cyan">
							RSEngine
						</Text>
						<Text color="gray">
							{columns < 72 ? 'Interactive mode' : 'Interactive watermark terminal'}  ·  {statusLabel(status)}
						</Text>
					</>
				)}
			</Box>

			<Box height={gapBelowHeader}>
				<Text> </Text>
			</Box>

			<Box flexDirection="column" height={middleHeight}>
				{hasEntries ? (
					<>
						<Text color="gray">{sessionLabel}</Text>
						{outputLeadGap > 0 ? (
							<Box height={outputLeadGap}>
								<Text> </Text>
							</Box>
						) : null}
						<Box flexDirection="column" height={visibleEntryCount}>
							<OutputLog entries={visibleEntries} />
						</Box>
					</>
				) : (
					<Box flexDirection="column" height={middleHeight}>
						{showQuickStart
							? quickStartLines.map((line, index) => (
									<Text key={`${index}-${line}`} color="gray">
										{line === ' '
											? ' '
											: index > 0 && line !== 'Quick start'
												? `- ${line}`
												: line}
									</Text>
								))
							: null}
					</Box>
				)}
			</Box>

			<Box height={gapAboveInput}>
				<Text> </Text>
			</Box>

			<Box flexDirection="column" height={inputHeight}>
				<CommandInput
					isDisabled={isRunning}
					onSubmit={handleSubmit}
					placeholder={shellPlaceholder}
					hint={
						isRunning
							? 'Command is running...'
							: columns < 72
								? 'Enter submits · Up/Down recalls history'
								: 'Press Enter to run · Up/Down recalls history · clear resets the session'
					}
				/>
			</Box>

			<Box height={gapAboveFooter}>
				<Text> </Text>
			</Box>

			<Box height={footerHeight}>
				<Text color={statusColor(status)}>
					{columns < 72
						? `Status: ${statusLabel(status)}`
						: `Status: ${statusLabel(status)}  ·  ${
								hasEntries ? 'Backend: native rse CLI' : 'Tip: run help to see available commands'
							}`}
				</Text>
			</Box>
		</Box>
	);
}
