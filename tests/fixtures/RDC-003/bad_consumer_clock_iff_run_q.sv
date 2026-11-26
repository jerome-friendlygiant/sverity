// sverity-args: --top top --sdc @DIR@/two_input.sdc
// revert: posedge clk iff run_q or => posedge clk or
// revert-expect-none: RDC-003 WARNING
// run_q gates the clock event of q2_q with iff: q2_q loads 1 at the next edge only if run_q released early.
// expect: RDC-003 WARNING
module top(input logic clk, input logic por_ni, input logic rst2_ni, input logic d_i, input logic en_i, output logic q_o);
  logic [1:0] boot_q; logic run_q, q2_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) boot_q <= 2'b00; else boot_q <= {boot_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) run_q <= 1'b0; else run_q <= d_i;
  always_ff @(posedge clk iff run_q or negedge rst2_ni) if (!rst2_ni) q2_q <= 1'b0; else q2_q <= 1'b1;
  assign q_o = q2_q;
endmodule
