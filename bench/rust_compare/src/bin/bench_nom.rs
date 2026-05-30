use rust_compare::{bench, read_fixture};
use nom::{
    branch::alt, bytes::complete::*, character::complete::*,
    combinator::*, multi::*, sequence::*, IResult,
};

fn ws(i: &str) -> IResult<&str, &str> { multispace0(i) }
fn lit<'a>(s: &'static str) -> impl FnMut(&'a str) -> IResult<&'a str, &'a str> {
    move |i| { let (i, _) = ws(i)?; tag(s)(i) }
}
fn json_str(i: &str) -> IResult<&str, &str> {
    let (i, _) = ws(i)?;
    let (i, _) = char('"')(i)?;
    let (i, s) = take_while(|c: char| c != '"' && c != '\\')(i)?;
    let (i, _) = char('"')(i)?;
    Ok((i, s))
}
fn json_num(i: &str) -> IResult<&str, &str> {
    let (i, _) = ws(i)?;
    let (i, _) = nom::number::complete::recognize_float(i)?;
    Ok((i, ""))
}
fn value(i: &str) -> IResult<&str, ()> {
    let (i, _) = ws(i)?;
    alt((
        map(object, |_| ()),
        map(array, |_| ()),
        map(json_str, |_| ()),
        map(json_num, |_| ()),
        map(tag("true"), |_| ()),
        map(tag("false"), |_| ()),
        map(tag("null"), |_| ()),
    ))(i)
}
fn pair_(i: &str) -> IResult<&str, ()> {
    let (i, _) = json_str(i)?;
    let (i, _) = lit(":")(i)?;
    value(i)
}
fn object(i: &str) -> IResult<&str, ()> {
    let (i, _) = lit("{")(i)?;
    let (i, _) = separated_list0(lit(","), pair_)(i)?;
    let (i, _) = lit("}")(i)?;
    Ok((i, ()))
}
fn array(i: &str) -> IResult<&str, ()> {
    let (i, _) = lit("[")(i)?;
    let (i, _) = separated_list0(lit(","), value)(i)?;
    let (i, _) = lit("]")(i)?;
    Ok((i, ()))
}

fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("nom parse", 100, || {
        let _ = value(&input);
    });
}
