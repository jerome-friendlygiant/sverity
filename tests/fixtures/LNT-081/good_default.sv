// sverity-args: --top top
// expect-none: LNT-081
module top(input logic [1:0] s, output logic y); always_comb begin unique case (s) default:y=0; endcase end endmodule
