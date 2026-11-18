// sverity-args: --top top
// The variable is assigned before the case: the empty default item leaves that value, so nothing is kept from an earlier activation.
// expect-none: LNT-013
module top(input logic [1:0] sel, input logic d, output logic y, z);
  always_comb begin
    y = 1'b0;
    z = 1'b0;
    unique case (sel)
      2'd0: y = d;
      2'd1: z = d;
      default: begin end
    endcase
  end
endmodule
