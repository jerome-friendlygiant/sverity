// sverity-args: --top top
// This procedure writes `s.a` only, and its default item assigns it; `s.b` is written by another procedure. Nothing is left unassigned
// in the bits this procedure writes.
// expect-none: LNT-013
module top(input logic [1:0] sel, input logic d, output logic a, b);
  typedef struct packed { logic a; logic b; } pair_t;
  pair_t s;
  always_comb begin
    unique case (sel)
      2'd0: s.a = d;
      2'd1: s.a = 1'b0;
      default: s.a = 1'b1;
    endcase
  end
  always_comb s.b = d;
  assign a = s.a;
  assign b = s.b;
endmodule
