import React from 'react';
import { Box, Text } from 'ink';

export type OutputLogEntry = {
	id: string;
	kind: 'command' | 'system' | 'hint' | 'error';
	text: string;
};

type OutputLogProps = {
	entries: OutputLogEntry[];
};

export default function OutputLog({ entries }: OutputLogProps) {
	if (entries.length === 0) {
		return null;
	}

	return (
		<Box flexDirection="column">
			{entries.map((entry) => (
				<Text
					key={entry.id}
					color={
						entry.kind === 'error'
							? 'red'
							: entry.kind === 'command'
								? 'white'
								: 'gray'
					}
					bold={entry.kind === 'command'}
				>
					{entry.text}
				</Text>
			))}
		</Box>
	);
}
