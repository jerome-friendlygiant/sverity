module top(input logic src_clk,
  dst_clk,
  local_a,
  local_b,
  
           output logic qa,
  qb);

  always_ff @(posedge src_clk) begin
    qa <= local_a;
  end
  always_ff @(posedge dst_clk) begin
    qb <= local_b;
  end

endmodule
