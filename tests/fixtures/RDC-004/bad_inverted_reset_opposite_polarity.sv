// sverity-args: --top top
// Double-inversion integration bug: parent uses negedge rst_n, child expects negedge rst_ni but receives ~rst_n.
// Same leaf with opposite effective assertion polarity -> distinct reset domains -> RDC-004 WARNING (and no RDC-011).
// revert: .rst_ni(~rst_n) => .rst_ni(rst_n)
// revert-expect-none: RDC-004 WARNING
// expect: RDC-004 WARNING
// expect-none: RDC-011
module child(input logic clk, rst_ni, d, output logic q);
  always_ff @(posedge clk or negedge rst_ni) if (!rst_ni) q <= 1'b0; else q <= d;
endmodule
module top(input logic clka, rst_n, d, output logic q);
  logic s;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s <= 1'b0; else s <= d;
  child u(.clk(clka), .rst_ni(~rst_n), .d(s), .q(q));
endmodule
