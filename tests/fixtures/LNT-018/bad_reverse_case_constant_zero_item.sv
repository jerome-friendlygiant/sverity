// sverity-args: --top top
// revert: 1'b0:    sel = 4'd2; => is_b:    sel = 4'd2;
// revert-expect-none: LNT-018
// Reverse-case idiom with one constant item: 1'b0 can never equal the case expression 1'b1, so its arm is unreachable.
// expect: LNT-018 WARNING 11
module top(input logic is_j, is_b, is_cj, output logic [3:0] sel);
  always_comb begin
    sel = 4'd0;
    unique case (1'b1)
      is_j:    sel = 4'd1;
      1'b0:    sel = 4'd2;
      is_cj:   sel = 4'd3;
      default: sel = 4'd0;
    endcase
  end
endmodule
