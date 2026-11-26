// Command relay that applies a producer command in the consumer domain after a toggled strobe.
module top (
  input  logic       clk_a,
  input  logic       rst_a_n,
  input  logic       clk_b,
  input  logic       rst_b_n,
  input  logic       cmd_wr_i,
  input  logic [2:0] cmd_i,
  output logic [2:0] applied_b_o
);
  logic [2:0] cmd_a_q, cmd_b1_q;
  logic       stb_a_q, stb_b1_q, stb_b_q;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      cmd_a_q <= '0;
      stb_a_q <= 1'b0;
    end else if (cmd_wr_i) begin
      cmd_a_q <= cmd_i;
      stb_a_q <= ~stb_a_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      stb_b1_q <= 1'b0;
      stb_b_q  <= 1'b0;
      cmd_b1_q <= '0;
    end else begin
      stb_b1_q <= stb_a_q;
      stb_b_q  <= stb_b1_q;
      cmd_b1_q <= cmd_a_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) applied_b_o <= '0;
    else if (stb_b1_q ^ stb_b_q) applied_b_o <= cmd_b1_q;

endmodule
