// sverity-args: --top top --sdc @DIR@/cut.sdc
// Near miss of bad_false_path_cut.sv: the destination register is named dst_r, so the set_false_path -to pattern q* no longer matches the crossing's destination.
// expect-none: CDC-038
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, dst_r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) dst_r <= 1'b0; else dst_r <= a_q;
  assign q = dst_r;
endmodule
