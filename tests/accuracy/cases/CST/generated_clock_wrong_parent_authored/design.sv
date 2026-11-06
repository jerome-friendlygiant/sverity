module rate_pll #(parameter int DIV = 4) (
  input  logic ref_i,
  input  logic rst_ni,
  output logic aux_o
);
  logic [DIV-1:0] phase_q;
  always_ff @(posedge ref_i or negedge rst_ni) begin
    if (!rst_ni) begin
      phase_q <= '0;
      aux_o <= 1'b0;
    end else begin
      phase_q <= phase_q + 1'b1;
      aux_o <= phase_q[DIV-1];
    end
  end
endmodule

module aux_receiver (
  input logic aux_clk_i,
  input logic rst_ni,
  input logic [7:0] payload_i,
  output logic [7:0] payload_o
);
  logic [7:0] payload_q;
  always_ff @(posedge aux_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      payload_q <= '0;
    else
      payload_q <= payload_i;
  end
  assign payload_o = payload_q;
endmodule

module top (
  input logic ref_clk_i,
  input logic unrelated_clk_i,
  input logic rst_ni,
  input logic [7:0] payload_i,
  output logic [7:0] payload_o
);
  logic aux_clk;
  rate_pll #(.DIV(4)) u_pll (.ref_i(ref_clk_i), .rst_ni, .aux_o(aux_clk));
  aux_receiver u_rx (.aux_clk_i(aux_clk), .rst_ni, .payload_i, .payload_o);
endmodule
