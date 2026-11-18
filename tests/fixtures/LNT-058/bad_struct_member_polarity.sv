// sverity-args: --top top
// expect: LNT-058 ERROR 5
typedef struct packed { logic rst_n; logic en; } ctl_t;
module top(input logic clk, input ctl_t c, input logic d, output logic q);
  always_ff @(posedge clk or negedge c.rst_n) if (c.rst_n) q <= 1'b0; else q <= d;
endmodule
