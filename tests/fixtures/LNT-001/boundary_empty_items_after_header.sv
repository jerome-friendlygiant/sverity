// sverity-args: --top top
// Near-miss of bad_syntax: the header is terminated with ';' and the legal empty items and end label must not be reported.
// expect-none: LNT-001
module top(input logic a, output logic y);;
  assign y = a;;
endmodule : top
