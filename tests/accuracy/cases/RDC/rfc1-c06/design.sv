// Fault monitor that mirrors a source-domain fault flag into a status latch for software readback.
module top (
  input  logic clk_a,
  input  logic rst_a_n,
  input  logic clk_b,
  input  logic rst_b_n,
  input  logic fault_i,
  input  logic clr_i,
  output logic status_b_o
);
  logic fault_a_q, fsync1_q, fsync_q, ra1_q, ra_b_q;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) fault_a_q <= 1'b0;
    else if (fault_i) fault_a_q <= 1'b1;
    else if (clr_i) fault_a_q <= 1'b0;

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      fsync1_q <= 1'b0;
      fsync_q  <= 1'b0;
      ra1_q    <= 1'b0;
      ra_b_q   <= 1'b0;
    end else begin
      fsync1_q <= fault_a_q;
      fsync_q  <= fsync1_q;
      ra1_q    <= rst_a_n;
      ra_b_q   <= ra1_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n || !ra_b_q) status_b_o <= 1'b0;
    else if (fsync_q) status_b_o <= 1'b1;

endmodule
