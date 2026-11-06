module event_source (
  input logic src_clk_i,
  input logic rst_ni,
  input logic event_i,
  output logic event_o
);
  always_ff @(posedge src_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      event_o <= 1'b0;
    else
      event_o <= event_i;
  end
endmodule

module event_sink (
  input logic dst_clk_i,
  input logic rst_ni,
  input logic event_i,
  output logic seen_o
);
  logic sync1_q;
  logic sync2_q;
  always_ff @(posedge dst_clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync1_q <= 1'b0;
      sync2_q <= 1'b0;
    end else begin
      sync1_q <= event_i;
      sync2_q <= sync1_q;
    end
  end
  assign seen_o = sync2_q;
endmodule

module top (
  input logic src_clk_i,
  input logic dst_clk_i,
  input logic rst_ni,
  input logic event_i,
  output logic seen_o
);
  logic source_event;
  event_source u_source (.src_clk_i, .rst_ni, .event_i, .event_o(source_event));
  event_sink u_sink (.dst_clk_i, .rst_ni, .event_i(source_event), .seen_o);
endmodule
