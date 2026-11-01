// sverity-args: --top top
// block-local temporaries are not module-level; a nonblocking read sees the old value by design; reads after the write are fine
// expect-none: LNT-044
module top(input logic clk, a, output logic q, r);
  logic t;
  always_comb begin
    logic u;
    u = a;
    r = u;
  end
  always_ff @(posedge clk) begin
    q <= t;
    t <= a;
  end
endmodule
