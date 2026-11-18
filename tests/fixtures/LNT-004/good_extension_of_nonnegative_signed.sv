// sverity-args: --top top
// A signed loop counter that takes only 0..3 extends into an unsigned target unchanged: its range holds no negative value.
// expect: LNT-004 INFO 8
// expect-none: LNT-004 WARNING
module top(input logic [3:0] d, output logic [15:0] y [4]);
  always_comb begin
    for (byte i = 0; i < 4; i++) begin
      y[i] = i;
    end
  end
endmodule
