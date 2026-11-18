// sverity-args: --top top
// `rst` reads active high by name, but the event list says it resets on the falling edge: `if (rst) <data> else <reset>`.
// expect-none: LNT-054
module top(input logic clk, rst, d, output logic y);
  logic q;
  always_ff @(posedge clk or negedge rst) if (rst) q <= d; else q <= 1'b0;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
