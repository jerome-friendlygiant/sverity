// Request handoff that moves a payload from a producer register into a consumer register.
module top (
  input  logic       clk_a,
  input  logic       rst_a_n,
  input  logic       clk_b,
  input  logic       rst_b_n,
  input  logic       req_i,
  input  logic [7:0] data_i,
  output logic [7:0] held_o
);
  logic       vld_a_q, vld_b1_q, vld_b_q;
  logic [7:0] pay_a_q, pay_b1_q, pay_b_q;

  always_ff @(posedge clk_a) vld_a_q <= req_i;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) pay_a_q <= '0;
    else if (req_i) pay_a_q <= data_i;

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      vld_b1_q <= 1'b0;
      vld_b_q  <= 1'b0;
      pay_b1_q <= '0;
      pay_b_q  <= '0;
    end else begin
      vld_b1_q <= vld_a_q;
      vld_b_q  <= vld_b1_q;
      pay_b1_q <= pay_a_q;
      pay_b_q  <= pay_b1_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) held_o <= '0;
    else if (vld_b_q) held_o <= pay_b_q;

endmodule
