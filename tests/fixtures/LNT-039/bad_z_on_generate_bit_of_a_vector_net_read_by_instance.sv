// sverity-args: --top top
// Each generate iteration drives one bit of the net 'z; no bit has a second driver, so the bit the instance reads floats.
// expect: LNT-039 WARNING 10
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  for (genvar g = 0; g < 2; g++) begin : gg
    assign t[g] = 1'bz;
  end
  sub u(.a(t[0]), .y(y));
endmodule
