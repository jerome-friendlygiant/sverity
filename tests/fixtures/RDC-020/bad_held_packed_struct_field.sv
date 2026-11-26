// sverity-args: --top top
// The reset branch gives keep its own value (held through reset) and clear a constant: keep is an unreset bit that feeds the
// reset register q (RDC-010) and the primary output (RDC-025), and v is only partly reset (RDC-020).
// expect: RDC-020 WARNING
// expect: RDC-010 WARNING
// expect: RDC-025 WARNING
module top(input logic clka, rst_n, d, output logic q, output wire held);
  typedef struct packed { logic keep; logic clear; } state_t;
  state_t v;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) v <= '{keep: v.keep, clear: 1'b0};
    else v <= '{keep: d, clear: d};
  assign held = v.keep;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= v.keep;
endmodule
