// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// revert: q2_q <= w; => q2_q <= 1'b0;
// revert-expect-none: RDC-003 WARNING
// run_q releases late and reaches q2_q through a continuous-assign gate; q2_q is masked only at the next edge, so the late release reaches q_o.
// expect: RDC-003 WARNING
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic q_o);
  logic [1:0] boot_q; logic run_q, q2_q, w;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) boot_q <= 2'b00; else boot_q <= {boot_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) run_q <= 1'b0; else run_q <= d_i;
  assign w = run_q & en_i;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q2_q <= 1'b0; else q2_q <= w;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q_o <= 1'b0; else q_o <= q2_q & boot_q[1];
endmodule
