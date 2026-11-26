// Token sender that issues only while a receiver-ready level from the receiving domain is set.
module top (
  input  logic       clk_a,
  input  logic       rst_a_n,
  input  logic       clk_b,
  input  logic       rst_b_n,
  input  logic [7:0] tok_i,
  output logic [7:0] tok_b_o,
  output logic       tok_v_b_o
);
  logic       rdy_b_q, rdy_a1_q, rdy_a_q;
  logic       tok_v_a_q, tok_v_b1_q, tok_v_b_q;
  logic [7:0] tok_a_q, tok_b1_q;

  // Receiver ready rises after its own reset release.
  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) rdy_b_q <= 1'b0;
    else          rdy_b_q <= 1'b1;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      rdy_a1_q <= 1'b0;
      rdy_a_q  <= 1'b0;
    end else begin
      rdy_a1_q <= rdy_b_q;
      rdy_a_q  <= rdy_a1_q;
    end

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      tok_v_a_q <= 1'b0;
      tok_a_q   <= '0;
    end else begin
      tok_v_a_q <= rdy_a_q;
      tok_a_q   <= tok_i;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      tok_v_b1_q <= 1'b0;
      tok_v_b_q  <= 1'b0;
      tok_b1_q   <= '0;
    end else begin
      tok_v_b1_q <= tok_v_a_q;
      tok_v_b_q  <= tok_v_b1_q;
      tok_b1_q   <= tok_a_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      tok_b_o   <= '0;
      tok_v_b_o <= 1'b0;
    end else begin
      tok_v_b_o <= tok_v_b_q;
      if (tok_v_b_q) tok_b_o <= tok_b1_q;
    end

endmodule
