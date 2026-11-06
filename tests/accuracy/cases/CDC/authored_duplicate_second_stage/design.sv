module split_sync (
  input  logic clk_dst,
  input  logic arst_n,
  input  logic async_event,
  output logic event_a,
  output logic event_b,
  output logic disagreement
);
  logic first_stage;
  logic second_a;
  logic second_b;

  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) first_stage <= 1'b0;
    else first_stage <= async_event;
  end

  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) begin
      second_a <= 1'b0;
      second_b <= 1'b0;
    end else begin
      second_a <= first_stage;
      second_b <= first_stage;
    end
  end

  assign event_a = second_a;
  assign event_b = second_b;
  assign disagreement = second_a ^ second_b;
endmodule

module event_router (
  input  logic clk_dst,
  input  logic arst_n,
  input  logic async_event,
  output logic route_fault,
  output logic [1:0] route_count
);
  logic event_a;
  logic event_b;
  logic disagreement;

  split_sync u_sync (
    .clk_dst(clk_dst), .arst_n(arst_n), .async_event(async_event),
    .event_a(event_a), .event_b(event_b), .disagreement(disagreement)
  );

  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) begin
      route_fault <= 1'b0;
      route_count <= 2'b0;
    end else begin
      route_fault <= disagreement;
      if (event_a) route_count <= route_count + 2'd1;
      else if (event_b) route_count <= route_count + 2'd2;
    end
  end
endmodule

module top (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic event_src,
  output logic route_fault,
  output logic [1:0] route_count
);
  event_router u_router (
    .clk_dst(dst_clk), .arst_n(arst_n), .async_event(event_src),
    .route_fault(route_fault), .route_count(route_count)
  );
endmodule
