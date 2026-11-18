// sverity-args: --top top
// expect: LNT-007 WARNING 5
`default_nettype wire
module top(input logic d, output logic y);
assign implicit_value = ~d;
assign y = implicit_value;
endmodule
`default_nettype none
