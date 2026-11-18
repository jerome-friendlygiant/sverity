// sverity-args: --top top
// The read-only inout is an input, but an output port nothing drives stays a WARNING.
// expect: LNT-008 WARNING 11
// expect-count: LNT-008 WARNING 1
module top(input logic [7:0] data, output wire [7:0] value, output wire [7:0] idle);
  wire [7:0] bus;
  assign bus = data;
  reader u_reader(.pad(bus), .result(value), .never(idle));
endmodule

module reader(inout wire [7:0] pad, output wire [7:0] result, output wire [7:0] never);
  assign result = pad;
endmodule
