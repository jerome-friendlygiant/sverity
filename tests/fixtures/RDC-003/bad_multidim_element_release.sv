// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// A multi-dimensional packed register: r_q[1] becomes 4'hE at the release edge and drives q_o directly. Element selects wider than one bit are not interpreted.
// expect: RDC-003 WARNING
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic [3:0] q_o);
  logic [1:0][3:0] r_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) r_q <= '0; else r_q[1] <= 4'hE;
  assign q_o = r_q[1];
endmodule
