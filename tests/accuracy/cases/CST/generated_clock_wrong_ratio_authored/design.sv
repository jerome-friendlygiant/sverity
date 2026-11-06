module programmable_divider #(parameter int DIVISOR = 3) (
  input logic clk_i,
  input logic rst_ni,
  output logic divided_clk_o
);
  localparam int CW = (DIVISOR <= 2) ? 1 : $clog2(DIVISOR);
  logic [CW-1:0] count_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      count_q <= '0;
      divided_clk_o <= 1'b0;
    end else if (count_q == DIVISOR-1) begin
      count_q <= '0;
      divided_clk_o <= ~divided_clk_o;
    end else begin
      count_q <= count_q + 1'b1;
    end
  end
endmodule

module slow_logic (
  input logic clk_i,
  input logic rst_ni,
  input logic in_i,
  output logic out_o
);
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      out_o <= 1'b0;
    else
      out_o <= in_i;
  end
endmodule

module top (
  input logic clk_i,
  input logic rst_ni,
  input logic in_i,
  output logic out_o
);
  logic slow_clk;
  programmable_divider #(.DIVISOR(3)) u_div (.clk_i, .rst_ni, .divided_clk_o(slow_clk));
  slow_logic u_logic (.clk_i(slow_clk), .rst_ni, .in_i, .out_o);
endmodule
