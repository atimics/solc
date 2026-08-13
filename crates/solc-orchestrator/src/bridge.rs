use solc_orchestrator::process::{
    invalid_utf8_response, oversized_request_response, process_request_line,
};
use std::io::{self, BufRead, Write};

const MAX_REQUEST_LINE_BYTES: usize = 16_384;

enum BoundedLine {
    Eof,
    Line(Vec<u8>),
    TooLong,
}

fn read_bounded_line(reader: &mut impl BufRead) -> io::Result<BoundedLine> {
    let mut line = Vec::new();
    let mut too_long = false;
    loop {
        let available = reader.fill_buf()?;
        if available.is_empty() {
            return if line.is_empty() && !too_long {
                Ok(BoundedLine::Eof)
            } else if too_long || line.len() > MAX_REQUEST_LINE_BYTES {
                Ok(BoundedLine::TooLong)
            } else {
                Ok(BoundedLine::Line(line))
            };
        }
        let take = available
            .iter()
            .position(|byte| *byte == b'\n')
            .map_or(available.len(), |position| position + 1);
        if !too_long && line.len() + take <= MAX_REQUEST_LINE_BYTES + 1 {
            line.extend_from_slice(&available[..take]);
        } else {
            too_long = true;
        }
        let found_newline = available[take - 1] == b'\n';
        reader.consume(take);
        if found_newline {
            if too_long {
                return Ok(BoundedLine::TooLong);
            }
            line.pop();
            if line.last() == Some(&b'\r') {
                line.pop();
            }
            return Ok(BoundedLine::Line(line));
        }
    }
}

fn main() -> io::Result<()> {
    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut reader = stdin.lock();
    let mut writer = stdout.lock();
    loop {
        let response = match read_bounded_line(&mut reader)? {
            BoundedLine::Eof => break,
            BoundedLine::TooLong => oversized_request_response(),
            BoundedLine::Line(line) => match std::str::from_utf8(&line) {
                Ok(line) => process_request_line(line),
                Err(_) => invalid_utf8_response(),
            },
        };
        writeln!(writer, "{response}")?;
        writer.flush()?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn bounded_reader_drains_oversized_lines_and_recovers() {
        let mut input = vec![b'x'; MAX_REQUEST_LINE_BYTES + 1];
        input.extend_from_slice(b"\n{}\n");
        let mut input = Cursor::new(input);
        assert!(matches!(
            read_bounded_line(&mut input).unwrap(),
            BoundedLine::TooLong
        ));
        match read_bounded_line(&mut input).unwrap() {
            BoundedLine::Line(line) => assert_eq!(line, b"{}"),
            _ => panic!("expected a recovered line"),
        }
        assert!(matches!(
            read_bounded_line(&mut input).unwrap(),
            BoundedLine::Eof
        ));

        let mut no_newline = Cursor::new(vec![b'x'; MAX_REQUEST_LINE_BYTES + 1]);
        assert!(matches!(
            read_bounded_line(&mut no_newline).unwrap(),
            BoundedLine::TooLong
        ));
    }
}
