module top(input logic src_clk,
  dst_clk,
  event_in,
  mask,
  
           output logic event_out,
  raw_seen);

  logic event_src, meta;
  always_ff @(posedge src_clk) event_src <= event_in;
  always_ff @(posedge dst_clk) begin
    meta <= event_src;
    event_out <= meta;
    raw_seen <= meta & mask;
  end

endmodule
