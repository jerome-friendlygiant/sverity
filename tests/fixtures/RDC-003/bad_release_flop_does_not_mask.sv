// sverity-args: --top top --sdc @DIR@/two_clock.sdc
// boot1_q is also a release flop for a clk2 synchroniser: its value after the first edge is 1, so it does not mask run_q at the next edge and the clk group must warn.
// expect: RDC-003 WARNING
// expect: RDC-003 INFO
module top(input logic clk, input logic clk2, input logic por_ni, input logic d_i, input logic en_i, output logic q_o, output logic [3:0] c_o);
  logic boot1_q, run_q; logic [1:0] s2_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) boot1_q <= 1'b0; else boot1_q <= 1'b1;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) run_q <= 1'b0; else run_q <= d_i;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) q_o <= 1'b0; else q_o <= run_q & boot1_q;
  always_ff @(posedge clk2 or negedge boot1_q) if (!boot1_q) s2_q <= 2'b00; else s2_q <= {s2_q[0], 1'b1};
  always_ff @(posedge clk2 or negedge s2_q[1]) if (!s2_q[1]) c_o <= '0; else c_o <= c_o + 4'd1;
endmodule
