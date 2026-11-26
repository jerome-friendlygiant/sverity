// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// revert: q_o <= run_q; => q_o <= run_q & boot_q[1];
// revert-expect-none: RDC-003 WARNING
// The late release of run_q reaches q_o at the next edge with nothing masking it.
// expect: RDC-003 WARNING
// expect: RDC-002 WARNING
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic q_o);
  logic [1:0] boot_q; logic run_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) boot_q <= 2'b00; else boot_q <= {boot_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) run_q <= 1'b0; else run_q <= d_i;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q_o <= 1'b0; else q_o <= run_q;
endmodule
