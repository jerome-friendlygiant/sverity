// sverity-args: --top top
// a read between the two assignments, a self-referencing right side, and a timing control keep the first live
// expect-none: LNT-053
module top(input logic clk, a, b, output logic y, z);
  logic t, u;
  always_comb begin
    t = a;
    y = t;
    t = b;
    u = a;
    u = u + b;
    z = u;
  end
  always_ff @(posedge clk) begin
    t <= a;
    y <= t;
    t <= b;
  end
endmodule
