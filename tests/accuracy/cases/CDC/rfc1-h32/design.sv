// Functional register released by a local reset synchronizer.
module top (
  input  logic clk_a,
  input  logic rst_ni,
  input  logic d_i,
  output logic q_o
);
  logic rst_s1_q;
  logic rst_s2_q;
  logic q_q;

  always_ff @(posedge clk_a or negedge rst_ni) begin
    if (!rst_ni) begin
      rst_s1_q <= 1'b0;
      rst_s2_q <= 1'b0;
    end else begin
      rst_s1_q <= 1'b1;
      rst_s2_q <= rst_s1_q;
    end
  end

  always_ff @(posedge clk_a or negedge rst_s2_q) begin
    if (!rst_s2_q) q_q <= 1'b0;
    else           q_q <= d_i;
  end

  assign q_o = q_q;
endmodule
