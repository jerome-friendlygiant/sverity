// sverity-args: --top top
// Only bit 0 of the net is driven 'z; bit 1 has its own driver. The bits are separate nets: bit 0 has no other driver and floats into the instance.
// expect: LNT-039 WARNING 9
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[0] = 1'bz;
  assign t[1] = x;
  sub u(.a(t[0]), .y(y));
endmodule
