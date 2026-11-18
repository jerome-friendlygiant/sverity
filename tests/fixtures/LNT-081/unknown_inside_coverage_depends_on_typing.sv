// sverity-args: --top top
// expect: LNT-081 WARNING 8
// 2-state selector. Per comparison, [-8:-1] is signed and covers 8..15 of the selector, so [0:7] and it cover every value.
// slang types the set as one unsigned expression, where [-8:-1] is 32-bit and covers none of 8..15. The readings disagree.
module top(input bit signed [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    priority case (s) inside
      [4'd0:4'd7]: y = 1'b1;
      [-8:-1]: y = 1'b0;
    endcase
  end
endmodule
