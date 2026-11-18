// sverity-args: --top top
// Every enumerator has an item: only an encoding no enumerator names can take the empty default, so the latch is an open question.
// expect-unresolved: LNT-013 11
typedef enum logic [1:0] {S0, S1, S2} st_e;
module top(input st_e sel, input logic d, output logic y);
  always_comb begin
    unique case (sel)
      S0: y = d;
      S1: y = 1'b0;
      S2: y = 1'b1;
      default: begin end
    endcase
  end
endmodule
