// sverity-args: --top top
// expect-none: LNT-018
// Reverse-case idiom: the constant case expression 1'b1 is matched against runtime conditions. Which arm runs is
// decided at run time, exactly like an ordinary case, so there is no constant condition to report.
module top(input logic is_j, is_b, is_cj, output logic [3:0] sel);
  always_comb begin
    sel = 4'd0;
    unique case (1'b1)
      is_j:    sel = 4'd1;
      is_b:    sel = 4'd2;
      is_cj:   sel = 4'd3;
      default: sel = 4'd0;
    endcase
  end
endmodule
