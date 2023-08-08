// SPDX-License-Identifier: MIT
use std::env;
use std::fs::File;
use std::io::{self, BufRead, BufReader, Write};

use ispoa::isp;

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();
    let ret = handle_command(&args);
    if ret.is_err() {
        println!("please read README");
    }
    ret
}

fn handle_command(args: &[String]) -> io::Result<()> {
    let default_trace_file = "trace.txt".to_owned();
    let trace_file = &args.get(1).unwrap_or(&default_trace_file);
    let f = File::open(trace_file)?;
    let f = BufReader::new(f);

    let ops = isp::get_op_list(f)?;

    println!(" -- summary --");
    ops.output_summary(io::stdout().lock())?;

    if args.len() == 3 && &args[2] == "-i" {
        shell(&ops)?;
    }

    Ok(())
}

struct Command {
    token: Vec<String>,
}

impl Command {
    fn from_string(string: String) -> Self {
        Command {
            token: string
                .split_ascii_whitespace()
                .map(|t| t.to_owned())
                .collect(),
        }
    }

    fn len(&self) -> usize {
        self.token.len()
    }
}

fn get_input(prompt: &str) -> io::Result<Command> {
    let mut out_handle = io::stdout().lock();
    let mut in_handle = io::stdin().lock();

    write!(out_handle, "{prompt}")?;
    out_handle.flush()?;

    let mut s = String::new();
    in_handle.read_line(&mut s)?;

    Ok(Command::from_string(s))
}

fn shell(ops: &isp::OpList) -> io::Result<()> {
    println!(" -- entering shell mode --");
    while let Ok(command) = get_input("> ") {
        if command.len() == 0 {
            continue;
        }

        match command.token[0].as_ref() {
            "p" => print_pipeline(ops, &command)?,
            "o" => print_operation(ops, &command)?,
            "q" => break,
            "h" => print_help()?,
            _ => println!("unknown commands: h for help"),
        }
    }
    Ok(())
}

fn print_help() -> io::Result<()> {
    let mut out_handle = io::stdout().lock();

    writeln!(out_handle, "p: print pipeline numbers")?;
    writeln!(
        out_handle,
        "o <pipeline_id> [<operation id>]: print operation numbers & info"
    )?;
    writeln!(out_handle, "q: quit")?;
    Ok(())
}

fn print_pipeline(ops: &isp::OpList, _command: &Command) -> io::Result<()> {
    println!("[pipelines]");
    ops.output_pipeline_numbers(io::stdout().lock())
}

fn print_operation(ops: &isp::OpList, command: &Command) -> io::Result<()> {
    if command.len() > 1 {
        let pipeline_id = command.token[1].parse::<isp::IdType>().unwrap();
        if command.len() == 2 {
            ops.output_operation_numbers(io::stdout().lock(), pipeline_id)?;
        }
        if command.len() == 3 {
            let op_id = command.token[2].parse::<isp::IdType>().unwrap();
            ops.output_operation(io::stdout().lock(), pipeline_id, op_id)?;
        }
    }

    Ok(())
}
