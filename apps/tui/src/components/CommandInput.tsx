import React, { useEffect, useState } from 'react';
import { Box, Text, useInput } from 'ink';

type CommandInputProps = {
	hint: string;
	isDisabled: boolean;
	onSubmit: (command: string) => void | Promise<void>;
	placeholder: string;
};

const PROMPT = '➜  ~ ';

export default function CommandInput({ hint, isDisabled, onSubmit, placeholder }: CommandInputProps) {
	const [value, setValue] = useState('');
	const [history, setHistory] = useState<string[]>([]);
	const [historyIndex, setHistoryIndex] = useState<number | null>(null);
	const [showCursor, setShowCursor] = useState(true);

	useEffect(() => {
		const intervalId = setInterval(() => {
			setShowCursor((currentValue) => !currentValue);
		}, 500);

		return () => {
			clearInterval(intervalId);
		};
	}, []);

	useInput((input, key) => {
		if (isDisabled) {
			return;
		}

		if (key.return) {
			const command = value.trim();
			if (command.length === 0) {
				return;
			}

			void onSubmit(command);
			setHistory((currentHistory) => [...currentHistory, command]);
			setHistoryIndex(null);
			setValue('');
			return;
		}

		if (key.upArrow) {
			if (history.length === 0) {
				return;
			}

			const nextIndex = historyIndex === null ? history.length - 1 : Math.max(0, historyIndex - 1);
			setHistoryIndex(nextIndex);
			setValue(history[nextIndex] ?? '');
			return;
		}

		if (key.downArrow) {
			if (history.length === 0 || historyIndex === null) {
				return;
			}

			const nextIndex = historyIndex + 1;
			if (nextIndex >= history.length) {
				setHistoryIndex(null);
				setValue('');
				return;
			}

			setHistoryIndex(nextIndex);
			setValue(history[nextIndex] ?? '');
			return;
		}

		if (key.backspace || key.delete) {
			setValue((currentValue) => currentValue.slice(0, -1));
			return;
		}

		if (key.ctrl || key.meta || key.escape || key.tab) {
			return;
		}

		if (input.length > 0) {
			if (historyIndex !== null) {
				setHistoryIndex(null);
			}
			setValue((currentValue) => currentValue + input);
		}
	});

	return (
		<Box flexDirection="column">
			<Box>
				<Text bold color={isDisabled ? 'gray' : 'cyan'}>
					{PROMPT}
				</Text>
				{value.length === 0 ? (
					<>
						<Text color={isDisabled ? 'gray' : undefined}>{showCursor ? '▌' : ' '}</Text>
						<Text dimColor>{placeholder}</Text>
					</>
				) : (
					<>
						<Text color={isDisabled ? 'gray' : undefined}>{value}</Text>
						<Text color={isDisabled ? 'gray' : undefined}>{showCursor ? '▌' : ' '}</Text>
					</>
				)}
			</Box>
			<Text color="gray">{hint}</Text>
		</Box>
	);
}
