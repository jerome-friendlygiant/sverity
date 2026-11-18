// sverity-args: --top top
// expect-confidence: LNT-060 established 8
module top(input logic clk, output logic a, b);
  typedef struct packed { logic left; logic right; } pair_t;
  pair_t q;
  always @(posedge clk) q.right = 1;
  assign a = q.left;
  assign b = q.right;
endmodule
