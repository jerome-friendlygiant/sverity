// Token sender that streams a counter into a receiver in another clock domain after reset release.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_ni,
  output logic [7:0] tok_b_o,
  output logic       tok_v_b_o
);
  logic       rst_a_n, rst_b_n, rb1_q, rb2_q;
  logic       tok_v_a_q, tok_v_b1_q, tok_v_b_q;
  logic [7:0] cnt_a_q, tok_b1_q;

  // Sender-domain reset release.
  always_ff @(posedge clk_a or negedge rst_ni)
    if (!rst_ni) rst_a_n <= 1'b0;
    else         rst_a_n <= 1'b1;

  // Receiver-domain reset release.
  always_ff @(posedge clk_b or negedge rst_ni)
    if (!rst_ni) {rst_b_n, rb2_q, rb1_q} <= '0;
    else         {rst_b_n, rb2_q, rb1_q} <= {rb2_q, rb1_q, 1'b1};

  // Sender streams a token every cycle once its own reset is released.
  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      tok_v_a_q <= 1'b0;
      cnt_a_q   <= '0;
    end else begin
      tok_v_a_q <= 1'b1;
      cnt_a_q   <= cnt_a_q + 8'd1;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      tok_v_b1_q <= 1'b0;
      tok_v_b_q  <= 1'b0;
      tok_b1_q   <= '0;
    end else begin
      tok_v_b1_q <= tok_v_a_q;
      tok_v_b_q  <= tok_v_b1_q;
      tok_b1_q   <= cnt_a_q;
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
