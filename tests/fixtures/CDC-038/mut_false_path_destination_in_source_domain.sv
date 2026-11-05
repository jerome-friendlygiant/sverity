// sverity-args: --top top --sdc @DIR@/cut.sdc
// MUTATION of bad_false_path_cut.sv: the destination register is clocked by the source clock, so no crossing is left under the false path.
// expect: CDC-038 WARNING
// revert: always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; => always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0;
// revert-expect-none: CDC-038 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= a_q;
endmodule
