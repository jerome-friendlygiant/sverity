// sverity-args: --top top
// The default item is written and empty: values the items do not name (S2, S3 of this enum) reach it and `y` keeps its old value.
// `unique` does not make that path dead for the latch.
// expect: LNT-013 WARNING 15
// revert: default: begin end => default: y = 1'b0;
// revert-expect-none: LNT-013 WARNING
module top(input logic [1:0] sel_i, input logic d, output logic y);
  typedef enum logic [1:0] {S0, S1, S2, S3} st_e;
  st_e sel;
  assign sel = st_e'(sel_i);
  always_comb begin
    unique case (sel)
      S0: y = d;
      S1: y = 1'b0;
      default: begin end
    endcase
  end
endmodule
