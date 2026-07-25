#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "generation.hpp"

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cerr << "Usage:\ndefuse <filename.c4>\n";
		return EXIT_FAILURE;
	}

	std::string contents;
	{
		std::stringstream contents_stream;
		std::fstream input(argv[1], std::ios::in);
		contents_stream << input.rdbuf();
		contents = contents_stream.str();
	}

	Tokenizer tokenizer(std::move(contents));
	std::vector<Token> tokens = tokenizer.tokenize();

	Parser parser(std::move(tokens));
	auto prog = parser.parse_prog();
	if (!prog.has_value()) {
		std::cerr << "Invalid program" << std::endl;
		return EXIT_FAILURE;
	}

	{
		Generator generator(prog.value());
		std::fstream file("out.asm", std::ios::out);
		file << generator.gen_prog();
	}

	system("nasm -felf64 out.asm");
	system("ld -o out out.o");
	system("./out");
	//std::cout << "\nCompiled successfully (exit code " << formatCode << ")\n";
	//system("echo $?");

	return EXIT_SUCCESS;
}