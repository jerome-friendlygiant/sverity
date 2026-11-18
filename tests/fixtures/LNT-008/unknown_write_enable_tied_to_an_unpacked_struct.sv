// sverity-args: --top top
// The enable is a member of an unpacked struct the only instance ties to a constant aggregate: the analysis does not decide it.
// expect-unresolved: LNT-008 6
typedef struct { logic en; logic [1:0] m; } cfg_t;
module child(input logic clk, input cfg_t cfg, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (cfg.en) r <= d;
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .cfg('{en: 1'b0, m: 2'd0}), .d, .q);
endmodule
