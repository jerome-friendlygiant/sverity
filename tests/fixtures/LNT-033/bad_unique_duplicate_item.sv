// sverity-args: --top top
// revert: 2'd1: y = 1'b1; => 2'd2: y = 1'b1;
// revert-expect-none: LNT-033
// The second item repeats the value 2'd1, so the unique claim is false (overlap of two items).
// expect: LNT-033 ERROR
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s)
      2'd0: y = 1'b1;
      2'd1: y = 1'b0;
      2'd1: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule
