"""
@file: trie_generator.py
@brief: Generates a trie data structure for SCPI commands from a YAML definition.
@copyright: Copyright (c) 2025 MTA, Inc.

This script reads a YAML file containing the definitions of an arbitrary number of
SCPI commands and outputs a C++ file containing a trie data structure that can be 
used to efficiently lookup and execute those commands.

For usage information, run the script with the `--help` option.
For information on the expected YAML format, refer to the documentation or the example YAML file.
"""

import argparse
import re
import sys

from dataclasses import dataclass
from itertools import product
from typing import Any, List, Optional

try:
    import yaml
except ImportError:
    print("PyYAML is required. Install it with: pip install PyYAML")
    sys.exit(1)


@dataclass
class SCPIDefinitionParameter:
    """Represents a parameter in a SCPI command definition."""
    name: str
    type: str
    description: str
    default: Optional[Any] = None
    choices: Optional[List[str]] = None

    def validate(self) -> None:
        """Validate the parameter to ensure it meets the SCPI definition requirements."""
        if self.type not in ['string', 'number', 'boolean', 'enum', 'arbitrarydata']:
            raise ValueError(
                f"Invalid type '{self.type}' for parameter '{self.name}'"
            )

        if self.choices and not isinstance(self.choices, list):
            raise ValueError(
                f"Choices for parameter '{self.name}' must be a list"
            )

        if self.default is not None and self.type == 'number' \
                and not isinstance(self.default, (int, float)):
            raise ValueError(
                f"Default value for parameter '{self.name}' must be a number"
            )

        if self.default is not None and self.type == 'string' \
                and not isinstance(self.default, str):
            raise ValueError(
                f"Default value for parameter '{self.name}' must be a string"
            )

        if self.default is not None and self.type == 'boolean' \
                and not isinstance(self.default, bool):
            raise ValueError(
                f"Default value for parameter '{self.name}' must be a boolean"
            )

        if self.type == 'enum' and self.choices and self.default is not None \
                and self.default not in self.choices:
            raise ValueError(
                f"Default value '{self.default}' for parameter '{self.name}' must be one of {self.choices}"
            )

        if self.type == 'enum' and (not self.choices or len(self.choices) == 0):
            raise ValueError(
                f"Enum parameter '{self.name}' must have at least one choice"
            )

        if self.type == 'arbitrarydata':
            if self.default is not None:
                raise ValueError(
                    "Arbitrary data parameters cannot have default values"
                )
            if self.choices is not None:
                raise ValueError(
                    "Arbitrary data parameters cannot have choices"
                )

        if not re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', self.name):
            raise ValueError(
                f"Invalid parameter name '{self.name}'. It must start with a letter or underscore and contain only alphanumeric characters and underscores."
            )

    def __str__(self) -> str:
        """String representation of the parameter for debugging."""
        result = f"{self.type}, name='{self.name}', description='{self.description}'"

        if self.default is not None:
            result += f", default={self.default}"

        if self.choices is not None:
            result += f", choices={self.choices}"

        # Add note for ABD parameters
        if self.type == 'arbitrarydata':
            result += " (Arbitrary Data Block)"

        return result

    @classmethod
    def from_dict(cls, data: dict) -> 'SCPIDefinitionParameter':
        """Create a SCPIDefinitionParameter from a dictionary."""
        return cls(
            name=data['name'],
            type=data['type'],
            description=data['description'],
            default=data.get('default'),
            choices=data.get('choices')
        )


@dataclass
class SCPIDefinitionCommand:
    """Represents a SCPI command definition, including its syntax, description, response, handler, and parameters."""
    syntax: str
    description: str
    response: Optional[str] = None
    handler: Optional[str] = None
    parameters: Optional[List[SCPIDefinitionParameter]] = None

    def validate(self) -> None:
        """Validate the SCPIDefinitionCommand to ensure all fields are correctly set."""
        if not self.syntax or not isinstance(self.syntax, str):
            raise ValueError("Command syntax must be a non-empty string")

        # Validate that the syntax is a valid hierarchical SCPI command format
        # Pattern: optional '*', then one or more command segments separated by ':', each segment as described, optional '?'
        if not re.match(r'^([\*]?([A-Z_0-9]+[a-z_0-9]*)(:[A-Z_0-9]+[a-z_0-9]*)*)(\?)?$', self.syntax):
            raise ValueError(f"Invalid SCPI command syntax '{self.syntax}'")

        if (not self.handler and not self.response) or (self.handler and self.response):
            raise ValueError(
                "Command must have either a handler or a response, but not both")

        if self.response and not isinstance(self.response, str):
            raise ValueError("Command response must be a string if provided")

        if self.handler and not isinstance(self.handler, str):
            raise ValueError("Command handler must be a string if provided")

        if self.parameters:
            for param in self.parameters:
                param.validate()

    def __str__(self) -> str:
        """String representation of the command for debugging."""
        params_str = '\n         - '.join(
            str(param) for param in self.parameters) if self.parameters else 'None'
        return f"""handler='{self.handler}' 
    parameters=
         - {params_str}"""

    @classmethod
    def from_dict(cls, data: dict) -> 'SCPIDefinitionCommand':
        """Create a SCPIDefinitionCommand from a dictionary, properly instantiating parameters."""
        parameters = None
        if 'parameters' in data and data['parameters']:
            parameters = [SCPIDefinitionParameter.from_dict(
                param) for param in data['parameters']]

        return cls(
            syntax=data['syntax'],
            description=data['description'],
            response=data.get('response'),
            handler=data.get('handler'),
            parameters=parameters
        )


@dataclass
class SCPIDefinition:
    """
    Represents a SCPI definition containing a namespace, class name, output file, and a list of commands.
    This class provides methods to validate the definition and create an instance from a dictionary.
    """

    namespace: str
    class_name: str
    output_file: str
    commands: List[SCPIDefinitionCommand]

    def validate(self) -> None:
        """Validate the SCPIDefinition to ensure all fields are correctly set."""
        if not self.namespace or not isinstance(self.namespace, str):
            raise ValueError("Namespace must be a non-empty string")

        # Validate that the namespace is a valid C++ namespace format
        if not re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*(::?[a-zA-Z_][a-zA-Z0-9_]*)*$', self.namespace):
            raise ValueError(f"Invalid namespace '{self.namespace}'")

        if not self.class_name or not isinstance(self.class_name, str):
            raise ValueError("Class name must be a non-empty string")

        if not self.output_file or not isinstance(self.output_file, str):
            raise ValueError("Output file must be a non-empty string")

        if not self.commands or not isinstance(self.commands, list):
            raise ValueError("Commands must be a non-empty list")

        for command in self.commands:
            command.validate()

    @classmethod
    def from_dict(cls, data: dict) -> 'SCPIDefinition':
        """Create a SCPIDefinition from a dictionary, properly instantiating commands."""
        commands = []
        if 'commands' in data and data['commands']:
            commands = [SCPIDefinitionCommand.from_dict(
                cmd) for cmd in data['commands']]

        return cls(
            namespace=data['namespace'],
            class_name=data['class_name'],
            output_file=data['output_file'],
            commands=commands
        )


class SCPITrieNode:
    """
    A node in the trie data structure for SCPI commands.

    Each node represents a character in the command syntax and can have multiple children.
    If a node is terminal, it indicates the end of a command syntax.
    """

    def __init__(self, character: str = '', terminal: bool = False):
        self.character = character
        self.children = {}
        self.terminal = terminal
        self.command_index: Optional[int] = None

    def add_child(self, node: 'SCPITrieNode') -> None:
        """Add a child node to this node."""
        if node.character in self.children:
            raise ValueError(
                f"Child node with character '{node.character}' already exists")
        self.children[node.character] = node


class SCPITrie:
    def __init__(self, commands: List[SCPIDefinitionCommand]):
        self.root = SCPITrieNode()
        self.commands = commands

        # Insert all commands into the trie
        for command in commands:
            self._insert_command(command)

    def _insert_command(self, command: SCPIDefinitionCommand) -> None:
        """Insert a command into the trie."""
        # Handle the command syntax character by character to preserve special characters
        syntax = command.syntax

        # Generate all possible variations of the command
        variations = self._generate_command_variations(syntax)

        # Insert each variation into the trie
        for variation in variations:
            current_node = self.root
            for char in variation:
                if char not in current_node.children:
                    new_node = SCPITrieNode(character=char)
                    current_node.add_child(new_node)
                current_node = current_node.children[char]

            # Mark the final node as terminal and assign the handler
            current_node.terminal = True
            current_node.command_index = self.commands.index(command)

    def _generate_command_variations(self, syntax: str) -> List[str]:
        """Generate all possible variations of a command syntax."""
        variations = []

        # Split the syntax into segments, preserving the separators
        segments = []
        current_segment = ""

        for char in syntax:
            if char in ('*', ':', '?'):
                if current_segment:
                    segments.append(current_segment)
                    current_segment = ""
                segments.append(char)
            else:
                current_segment += char

        if current_segment:
            segments.append(current_segment)

        # Generate variations for each segment
        segment_variations = []
        for segment in segments:
            if segment in ('*', ':', '?'):
                # Special characters remain as-is
                segment_variations.append([segment])
            else:
                # Generate abbreviated and full forms
                match = re.match(r'([A-Z_0-9]+)([a-z_0-9]*)', segment)
                if match:
                    upper_part, lower_part = match.groups()
                    if lower_part:
                        segment_variations.append(
                            [upper_part, upper_part + lower_part])
                    else:
                        segment_variations.append([upper_part])
                else:
                    segment_variations.append([segment])

        # Generate all combinations using Cartesian product
        for combination in product(*segment_variations):
            variations.append(''.join(combination))

        # Convert all variations to uppercase as SCPI commands are case-insensitive
        variations = [var.upper() for var in variations]

        return variations

    def __str__(self) -> str:
        """String representation of the trie for debugging."""
        def _print_node(node: SCPITrieNode, prefix: str = '') -> str:
            result = ''
            if node.terminal and node.command_index is not None:
                result += f"""
{prefix} -> 
    {self.commands[node.command_index]}\n"""
            for child in node.children.values():
                result += _print_node(child, prefix + child.character)
            return result

        return _print_node(self.root)

    def generate_cpp_code(self, scpi_definition: SCPIDefinition) -> str:
        """Generate C++ code for the trie and commands."""
        code = self._generate_cpp_header(scpi_definition)
        code += self._generate_class_declaration(scpi_definition)
        code += self._generate_scpi_namespace_start()
        code += self._generate_memory_comment(scpi_definition)
        code += self._generate_parameter_descriptors(scpi_definition)
        code += self._generate_trie_structure(self.root, scpi_definition)
        code += self._generate_commands_array(scpi_definition)
        code += self._generate_cpp_footer()
        return code

    def _generate_trie_structure(self, root: SCPITrieNode, scpi_definition: SCPIDefinition) -> str:
        """Generate the nested trie structure in C++."""
        code = "    // Trie structure\n"

        # Generate all nodes with their children arrays
        node_definitions = []
        root_def = self._collect_node_definitions(root, node_definitions)

        # Output all the node definitions
        for node_def in node_definitions:
            code += node_def + "\n"

        # Output the root node
        code += "    template<>\n"
        code += f"    const TrieNode T76::SCPI::Interpreter<{scpi_definition.namespace}::{scpi_definition.class_name}>::_trie = {root_def};\n"

        code += "\n"
        return code

    def _collect_node_definitions(self, node: SCPITrieNode, definitions: list, node_path: str = "") -> str:
        """Recursively collect all node definitions."""
        # First, collect definitions for all children
        child_names = []
        if node.children:
            sorted_children = sorted(
                node.children.values(), key=lambda n: n.character)
            for child in sorted_children:
                child_path = node_path + child.character
                child_name = self._collect_node_definitions(
                    child, definitions, child_path)
                child_names.append(child_name)

        # Generate the children array if there are children
        if child_names:
            children_array_name = f"{self._generate_node_name(node_path)}_children"
            children_def = f"    const TrieNode {children_array_name}[] = {{\n"
            for i, child_name in enumerate(child_names):
                # Split the child_name into node definition and comment
                if " // " in child_name:
                    node_part, comment_part = child_name.split(" // ", 1)
                    children_def += f"        {node_part}"
                    if i < len(child_names) - 1:
                        children_def += ","
                    children_def += f" // {comment_part}"
                else:
                    children_def += f"        {child_name}"
                    if i < len(child_names) - 1:
                        children_def += ","
                children_def += "\n"
            children_def += "    };"
            definitions.append(children_def)
            children_ref = children_array_name
        else:
            children_ref = "nullptr"

        # Generate this node's definition
        flags = []
        if node.terminal:
            flags.append("TrieNodeFlags::Terminal")
        flags_str = "uint8_t(" + " | ".join(flags) + ")" if flags else "0"

        # Handle special characters
        char = node.character
        if char == '\0' or char == '':
            char_str = "'\\0'"
        elif char == "'":
            char_str = "'\\''"
        elif char == '\\':
            char_str = "'\\\\'"
        else:
            char_str = f"'{char}'"

        cmd_index = node.command_index if node.command_index is not None else 0
        child_count = len(child_names)

        node_def = f"{{ {char_str}, {flags_str}, {child_count}, {children_ref}, {cmd_index} }}"

        # Add comment for terminal nodes
        if node.terminal and node.command_index is not None:
            command = self.commands[node.command_index]
            node_def += f" // Terminal: {command.syntax}"

        return node_def

    def _generate_node_name(self, node_path: str) -> str:
        """Generate a unique name for a node based on its path."""
        if not node_path:
            return "_root"

        # Create a safe identifier from the path
        safe_path = ""
        for char in node_path:
            if char.isalnum():
                safe_path += char
            elif char in ('*', ':', '?'):
                safe_path += {'*': '_star',
                              ':': '_colon', '?': '_question'}[char]
            else:
                safe_path += f"_{ord(char)}"

        return f"_node_{safe_path}"

    def _generate_cpp_header(self, scpi_definition: SCPIDefinition) -> str:
        """Generate the C++ file header."""
        return f'''/**
 * @file {scpi_definition.output_file}
 * 
 * Autogenerated SCPI commands trie and handler pointers.
 * Generated from {scpi_definition.class_name} definition.
 * 
 */

#include <t76/scpi_command.hpp>
#include <t76/scpi_trie.hpp>
#include <t76/scpi_interpreter.hpp>

'''

    def _generate_class_declaration(self, scpi_definition: SCPIDefinition) -> str:
        """Generate the class declaration with all handler method signatures."""
        code = f"namespace {scpi_definition.namespace} {{\n"
        code += f"    class {scpi_definition.class_name} {{\n"
        code += "    public:\n"

        for command in scpi_definition.commands:
            if command.handler:
                handler_name = command.handler
                code += f"        void {handler_name}(const std::vector<T76::SCPI::ParameterValue> &);\n"

        code += "    };\n"
        code += "}\n\n"
        return code

    def _generate_scpi_namespace_start(self) -> str:
        """Generate the start of the T76::SCPI namespace."""
        return "namespace T76::SCPI {\n\n"

    def _generate_parameter_descriptors(self, scpi_definition: SCPIDefinition) -> str:
        """Generate parameter descriptor arrays for each command."""
        code = "    // Parameter descriptors for each command\n"

        # First, generate all the choice arrays
        for i, command in enumerate(scpi_definition.commands):
            if command.parameters:
                for j, param in enumerate(command.parameters):
                    if param.choices:
                        code += f"    const char* const command_{i}_param_{j}_choices[] = {{\n"
                        for choice in param.choices:
                            code += f"        \"{choice}\",\n"
                        code += "    };\n\n"

        # Then generate the parameter descriptors
        for i, command in enumerate(scpi_definition.commands):
            if command.parameters:
                code += f"    const ParameterDescriptor command_{i}_params[] = {{\n"
                for j, param in enumerate(command.parameters):
                    code += "        {\n"

                    # Map parameter types to C++ enum values
                    type_mapping = {
                        'string': 'String',
                        'number': 'Number',
                        'boolean': 'Boolean',
                        'enum': 'Enum',
                        'arbitrarydata': 'ArbitraryData'
                    }
                    cpp_type = type_mapping.get(
                        param.type, param.type.capitalize())
                    code += f"            .type = ParameterType::{cpp_type},\n"

                    # Generate default value
                    if param.default is not None:
                        if param.type == 'string':
                            code += f"            .defaultValue = {{.stringValue = \"{param.default}\"}},\n"
                        elif param.type == 'number':
                            code += f"            .defaultValue = {{.numberValue = {param.default}}},\n"
                        elif param.type == 'boolean':
                            code += f"            .defaultValue = {{.booleanValue = {'true' if param.default else 'false'}}},\n"
                        elif param.type == 'enum':
                            code += f"            .defaultValue = {{.enumValue = \"{param.default}\"}},\n"
                        elif param.type == 'arbitrarydata':
                            # ABD parameters don't have defaults, but this shouldn't happen due to validation
                            code += "            .defaultValue = {.numberValue = 0},\n"
                    else:
                        code += "            .defaultValue = {.numberValue = 0},\n"

                    # Generate choices for enum parameters
                    if param.choices:
                        code += f"            .choiceCount = {len(param.choices)},\n"
                        code += f"            .choices = command_{i}_param_{j}_choices\n"
                    else:
                        code += "            .choiceCount = 0,\n"
                        code += "            .choices = nullptr\n"

                    code += "        },\n"
                code += "    };\n\n"

        return code

    def _generate_commands_array(self, scpi_definition: SCPIDefinition) -> str:
        """Generate the commands array in C++."""
        code = "    // Command handlers and parameters\n"
        code += "    template<>\n"
        code += f"    const Command<{scpi_definition.namespace}::{scpi_definition.class_name}> T76::SCPI::Interpreter<{scpi_definition.namespace}::{scpi_definition.class_name}>::_commands[] = {{\n"

        # Track maximum parameter count
        max_param_count = 0

        for i, command in enumerate(scpi_definition.commands):
            # Generate member function pointer syntax
            if command.handler:
                handler_ref = f"&{scpi_definition.namespace}::{scpi_definition.class_name}::{command.handler}"
            else:
                handler_ref = "nullptr"

            # Count parameters
            param_count = len(command.parameters) if command.parameters else 0
            max_param_count = max(max_param_count, param_count)

            # Generate parameter descriptor reference
            if command.parameters:
                param_ref = f"command_{i}_params"
            else:
                param_ref = "nullptr"

            code += f"        {{ {handler_ref}, {param_count}, {param_ref} }}, // {command.syntax}\n"

        code += "    };\n\n"

        # Generate command count constant
        code += "    template<>\n"
        code += f"    const size_t T76::SCPI::Interpreter<{scpi_definition.namespace}::{scpi_definition.class_name}>::_commandCount = {len(scpi_definition.commands)};\n\n"

        # Generate maximum parameter count constant
        code += "    template<>\n"
        code += f"    const size_t T76::SCPI::Interpreter<{scpi_definition.namespace}::{scpi_definition.class_name}>::_maxParameterCount = {max_param_count};\n\n"

        return code

    def _generate_cpp_footer(self) -> str:
        """Generate the C++ file footer."""
        return "} // namespace\n"

    def calculate_memory_usage(self, scpi_definition: SCPIDefinition) -> dict:
        """Calculate memory usage estimates."""
        # RP2350 memory characteristics
        rp2350_specs = {
            'total_flash': 4 * 1024 * 1024,  # 4MB flash
            'total_sram': 520 * 1024,        # 520KB SRAM
            'pointer_size': 4,               # 32-bit pointers
            'char_size': 1,                  # 8-bit chars
            'uint8_size': 1                  # 8-bit integers
        }

        # Calculate trie structure memory usage
        def count_nodes_and_arrays(node: SCPITrieNode) -> tuple:
            """Count total nodes and children arrays."""
            node_count = 1
            array_count = 1 if node.children else 0
            total_children = len(node.children)

            for child in node.children.values():
                child_nodes, child_arrays, child_children = count_nodes_and_arrays(
                    child)
                node_count += child_nodes
                array_count += child_arrays
                total_children += child_children

            return node_count, array_count, total_children

        total_nodes, total_arrays, total_children = count_nodes_and_arrays(
            self.root)

        # TrieNode struct size calculation
        # struct TrieNode {
        #     const uint8_t character;     // 1 byte
        #     const uint8_t flags;         // 1 byte
        #     const uint8_t childCount;    # 1 byte
        #     const TrieNode *children;    // 4 bytes (pointer)
        #     const uint8_t commandIndex;  // 1 byte
        # } __attribute__((packed));       # Total: 8 bytes per node

        trie_node_size = 8  # bytes per TrieNode (packed)
        trie_nodes_memory = total_nodes * trie_node_size

        # Parameter descriptors memory
        # type + defaultValue + choiceCount + choices pointer = 16 bytes
        param_descriptor_size = 4 + 4 + 4 + 4
        total_param_descriptors = sum(
            len(cmd.parameters) if cmd.parameters else 0 for cmd in scpi_definition.commands)
        param_descriptors_memory = total_param_descriptors * param_descriptor_size

        # Command array memory
        # struct Command {
        #     CommandHandler handler;      // 4 bytes (member function pointer)
        #     uint8_t parameterCount;      // 1 byte
        #     const ParameterDescriptor* parameters; // 4 bytes (pointer)
        # };                              // Total: 12 bytes per command (assuming 4-byte alignment)
        command_size = 12  # bytes per Command
        commands_memory = len(scpi_definition.commands) * command_size

        # String literals memory (approximate)
        # Parameter choices and other string constants
        string_memory = 0
        for cmd in scpi_definition.commands:
            if cmd.parameters:
                for param in cmd.parameters:
                    if param.choices:
                        for choice in param.choices:
                            # +1 for null terminator
                            string_memory += len(choice) + 1

        # Code memory (read-only, stored in flash)
        code_memory = trie_nodes_memory + \
            param_descriptors_memory + commands_memory + string_memory

        # Runtime memory (SRAM) - minimal, just pointers and state
        runtime_memory = 64  # Conservative estimate for interpreter state

        return {
            'rp2350_specs': rp2350_specs,
            'trie_stats': {
                'total_nodes': total_nodes,
                'total_arrays': total_arrays,
                'total_children': total_children,
                'node_size_bytes': trie_node_size
            },
            'memory_breakdown': {
                'trie_nodes': trie_nodes_memory,
                'param_descriptors': param_descriptors_memory,
                'commands': commands_memory,
                'string_literals': string_memory,
                'total_code': code_memory,
                'runtime_sram': runtime_memory
            },
            'utilization': {
                'flash_percent': (code_memory / rp2350_specs['total_flash']) * 100,
                'sram_percent': (runtime_memory / rp2350_specs['total_sram']) * 100
            }
        }

    def _generate_memory_comment(self, scpi_definition: SCPIDefinition) -> str:
        """Generate a comment block with memory usage information."""
        memory_usage = self.calculate_memory_usage(scpi_definition)

        comment = f"""/*
 * Memory Usage Estimate:
 * 
 * Trie Structure:
 *   - Total nodes: {memory_usage['trie_stats']['total_nodes']}
 *   - Children arrays: {memory_usage['trie_stats']['total_arrays']}
 *   - Node size: {memory_usage['trie_stats']['node_size_bytes']} bytes each
 *   - Trie memory: {memory_usage['memory_breakdown']['trie_nodes']} bytes
 * 
 * Command System:
 *   - Commands: {len(scpi_definition.commands)} ({memory_usage['memory_breakdown']['commands']} bytes)
 *   - Parameter descriptors: {memory_usage['memory_breakdown']['param_descriptors']} bytes
 *   - String literals: {memory_usage['memory_breakdown']['string_literals']} bytes
 * 
 * Total Memory Usage:
 *   - Code/Data (Flash): {memory_usage['memory_breakdown']['total_code']} bytes ({memory_usage['utilization']['flash_percent']:.2f}% of 2MB)
 *   - Runtime (SRAM): {memory_usage['memory_breakdown']['runtime_sram']} bytes ({memory_usage['utilization']['sram_percent']:.2f}% of 264KB)
 * 
 * Performance Characteristics:
 *   - Average lookup depth: ~{self.calculate_average_lookup_depth():.1f} character comparisons
 *   - Memory access pattern: Sequential (cache-friendly)
 *   - Space complexity: O(total_command_chars)
 */"""

        return comment

    def calculate_average_lookup_depth(self) -> float:
        """Calculate average character lookup depth for terminal commands."""
        def get_terminal_depths(node: SCPITrieNode, depth: int = 0) -> List[int]:
            depths = []
            if node.terminal:
                depths.append(depth)
            for child in node.children.values():
                depths.extend(get_terminal_depths(child, depth + 1))
            return depths

        terminal_depths = get_terminal_depths(self.root)
        return sum(terminal_depths) / len(terminal_depths) if terminal_depths else 0.0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate trie data structure for SCPI commands.")
    parser.add_argument(
        "input_file", help="Path to the input YAML file containing SCPI commands.")

    # Create mutually exclusive group for output options
    output_group = parser.add_mutually_exclusive_group(required=True)
    output_group.add_argument(
        "-o", "--output", dest="output_file",
        help="Path to the output file where the trie will be saved.")
    output_group.add_argument(
        "-i", "--info", action="store_true",
        help="Print trie structure and statistical information.")

    args = parser.parse_args()

    with open(args.input_file, 'r', encoding='utf-8') as f:
        scpi_commands = yaml.safe_load(f)
        definition = SCPIDefinition.from_dict(scpi_commands)
        definition.validate()

    trie = SCPITrie(definition.commands)

    if args.info:
        # Print trie structure and statistical information
        print("SCPI Command Trie Structure:")
        print("=" * 50)
        print(trie)

        # Print statistical information
        print("\nStatistical Information:")
        print("=" * 50)
        print(f"Total commands: {len(definition.commands)}")
        print(f"Namespace: {definition.namespace}")
        print(f"Class name: {definition.class_name}")

        # Count total nodes in trie
        def count_nodes(node: SCPITrieNode) -> int:
            count = 1
            for child in node.children.values():
                count += count_nodes(child)
            return count

        trie_total_nodes = count_nodes(trie.root)
        print(f"Total trie nodes: {trie_total_nodes}")

        # Count terminal nodes
        def count_terminal_nodes(node: SCPITrieNode) -> int:
            count = 1 if node.terminal else 0
            for child in node.children.values():
                count += count_terminal_nodes(child)
            return count

        terminal_nodes = count_terminal_nodes(trie.root)
        print(f"Terminal nodes: {terminal_nodes}")

        # Print memory usage information
        print("\nMemory Usage Estimate:")
        print("=" * 50)
        usage = trie.calculate_memory_usage(definition)

        print("Trie Structure:")
        print(
            f"  - Nodes: {usage['trie_stats']['total_nodes']} × {usage['trie_stats']['node_size_bytes']} bytes = {usage['memory_breakdown']['trie_nodes']} bytes")
        print(f"  - Arrays: {usage['trie_stats']['total_arrays']}")

        print("\nCommand System:")
        print(
            f"  - Commands: {len(definition.commands)} × 12 bytes = {usage['memory_breakdown']['commands']} bytes")
        print(
            f"  - Parameter descriptors: {usage['memory_breakdown']['param_descriptors']} bytes")
        print(
            f"  - String literals: {usage['memory_breakdown']['string_literals']} bytes")

        print("\nTotal Memory Usage:")
        print(
            f"  - Flash (code/data): {usage['memory_breakdown']['total_code']} bytes ({usage['utilization']['flash_percent']:.3f}% of 2MB)")
        print(
            f"  - SRAM (runtime): {usage['memory_breakdown']['runtime_sram']} bytes ({usage['utilization']['sram_percent']:.3f}% of 264KB)")

        print("\nPerformance:")
        print(
            f"  - Average lookup depth: {trie.calculate_average_lookup_depth():.1f} character comparisons")

        # List all recognized command variations
        print("\nAll recognized command variations:")
        print("-" * 40)

        def get_all_paths(node: SCPITrieNode, current_path: str = "") -> List[str]:
            paths = []
            if node.terminal:
                paths.append(current_path)
            for child in node.children.values():
                paths.extend(get_all_paths(
                    child, current_path + child.character))
            return paths

        all_paths = get_all_paths(trie.root)
        for path in sorted(all_paths):
            print(f"  {path}")

        print(f"\nTotal command variations: {len(all_paths)}")

    else:
        # Generate output file
        cpp_code = trie.generate_cpp_code(definition)
        with open(args.output_file, 'w', encoding='utf-8') as output_file:
            output_file.write(cpp_code)
        print(f"Generated C++ code written to: {args.output_file}")
