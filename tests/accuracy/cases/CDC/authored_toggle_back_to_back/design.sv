module toggle_sender #(
  parameter int WIDTH = 16
) (
  input logic src_clk,
  input logic arst_n,
  input logic request,
  input logic [WIDTH-1:0] data_in,
  output logic request_toggle,
  output logic [WIDTH-1:0] data_hold
);
  always_ff @(posedge src_clk or negedge arst_n) begin
    if (!arst_n) begin
      request_toggle <= 1'b0;
      data_hold <= '0;
    end else if (request) begin
      data_hold <= data_in;
      request_toggle <= ~request_toggle;
    end
  end
endmodule

module toggle_receiver #(
  parameter int WIDTH = 16
) (
  input logic dst_clk,
  input logic arst_n,
  input logic request_toggle_async,
  input logic [WIDTH-1:0] data_hold_async,
  output logic [WIDTH-1:0] data_out,
  output logic response_toggle
);
  logic req_meta;
  logic req_sync;
  always_ff @(posedge dst_clk or negedge arst_n) begin
    if (!arst_n) begin
      req_meta <= 1'b0;
      req_sync <= 1'b0;
      response_toggle <= 1'b0;
      data_out <= '0;
    end else begin
      req_meta <= request_toggle_async;
      req_sync <= req_meta;
      if (req_sync != response_toggle) begin
        data_out <= data_hold_async;
        response_toggle <= req_sync;
      end
    end
  end
endmodule

module top (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic request,
  input logic [15:0] data_in,
  output logic [15:0] data_out,
  output logic response_toggle
);
  logic request_toggle;
  logic [15:0] data_hold;
  toggle_sender #(.WIDTH(16)) u_sender (
    .src_clk(src_clk), .arst_n(arst_n), .request(request), .data_in(data_in),
    .request_toggle(request_toggle), .data_hold(data_hold)
  );
  toggle_receiver #(.WIDTH(16)) u_receiver (
    .dst_clk(dst_clk), .arst_n(arst_n),
    .request_toggle_async(request_toggle), .data_hold_async(data_hold),
    .data_out(data_out), .response_toggle(response_toggle)
  );
endmodule
