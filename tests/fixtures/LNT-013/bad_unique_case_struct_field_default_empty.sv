// sverity-args: --top top
// The procedure writes `s.a`, and its empty default item leaves it unassigned on that path.
// expect: LNT-013 WARNING 11
module top(input logic [1:0] sel, input logic d, output logic a, b);
  typedef struct packed { logic a; logic b; } pair_t;
  pair_t s;
  always_comb begin
    unique case (sel)
      2'd0: s.a = d;
      2'd1: s.a = 1'b0;
      default: begin end
    endcase
  end
  always_comb s.b = d;
  assign a = s.a;
  assign b = s.b;
endmodule
