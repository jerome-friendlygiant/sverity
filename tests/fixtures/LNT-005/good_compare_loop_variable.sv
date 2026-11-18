// sverity-args: --top top
// A loop variable that counts from 0 to 2 compared with a 2-bit unsigned value: both values lie in [0, 3], so both convert to the
// 32-bit unsigned comparison type unchanged. The finding is INFO with the value range.
// expect: LNT-005 INFO 9
// expect-none: LNT-005 WARNING
module top(input logic [1:0] sel, output logic y);
  always_comb begin
    y = 1'b0;
    for (int i = 0; i < 3; i++) if (sel == i) y = 1'b1;
  end
endmodule
