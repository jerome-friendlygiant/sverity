// sverity-args: --top top
// An inout the module only reads acts as an input and the net it is wired to supplies the value: INFO, not an undriven port.
// expect: LNT-008 INFO 11
// expect-none: LNT-008 WARNING
module top(input logic [7:0] data, output logic [7:0] value);
  wire [7:0] bus;
  assign bus = data;
  reader u_reader(.pad(bus), .result(value));
endmodule

module reader(inout wire [7:0] pad, output wire [7:0] result);
  assign result = pad;
endmodule
