// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// por_ni releases run_q whose D can differ from its reset value; its only consumer q_o is masked by the reset-chain bit boot_q[1] for one cycle.
// expect-none: RDC-003 WARNING
// expect-none: RDC-002
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic q_o);
  logic [1:0] boot_q; logic run_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) boot_q <= 2'b00; else boot_q <= {boot_q[0], 1'b1};
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) run_q <= 1'b0; else run_q <= d_i;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q_o <= 1'b0; else q_o <= run_q & boot_q[1];
endmodule
