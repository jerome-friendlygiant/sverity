// sverity-args: --top top
// `raw` takes any value through the cast, so S3 is not "never assigned": the relation is a gap, not a dead-state candidate.
// expect-unresolved: LNT-029 11
// expect-confidence: LNT-029 unresolved 11
module top(input logic clk, rst_n, input logic [1:0] code, output logic [1:0] o);
  typedef enum logic [1:0] {S0, S1, S2, S3} st_e;
  st_e raw, nxt, cur;
  assign raw = st_e'(code);
  always_comb begin
    nxt = cur;
    unique case (cur)
      S0: nxt = raw;
      S1: nxt = S2;
      S2: nxt = raw;
      default: nxt = S0;
    endcase
  end
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) cur <= S0; else cur <= nxt;
  assign o = cur;
endmodule
