// Four-phase request/acknowledge transfer with bundled payload.
module top (
  input  logic       src_clk,
  input  logic       dst_clk,
  input  logic       src_rst_ni,
  input  logic       dst_rst_ni,
  input  logic       send_i,
  input  logic [7:0] data_i,
  output logic       busy_o,
  output logic       valid_o,
  output logic [7:0] rx_o
);
  logic       req_q;
  logic       a1_q;
  logic       a2_q;
  logic [7:0] tx_q;
  logic       r1_q;
  logic       r2_q;
  logic       ack_q;
  logic [7:0] rx_q;
  logic       valid_q;

  always_ff @(posedge src_clk) begin
    if (!src_rst_ni) begin
      req_q <= 1'b0;
    end else if (!req_q && !a2_q && send_i) begin
      req_q <= 1'b1;
      tx_q  <= data_i;
    end else if (req_q && a2_q) begin
      req_q <= 1'b0;
    end
  end

  always_ff @(posedge src_clk) begin
    a1_q <= ack_q;
    a2_q <= a1_q;
  end

  always_ff @(posedge dst_clk) begin
    r1_q <= req_q;
    r2_q <= r1_q;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) begin
      ack_q   <= 1'b0;
      valid_q <= 1'b0;
    end else if (r2_q && !ack_q) begin
      ack_q   <= 1'b1;
      rx_q    <= tx_q;
      valid_q <= 1'b1;
    end else begin
      valid_q <= 1'b0;
      if (!r2_q) ack_q <= 1'b0;
    end
  end

  assign busy_o  = req_q;
  assign valid_o = valid_q;
  assign rx_o    = rx_q;
endmodule
