module top(input logic [3:0] request,
  output logic [1:0] index);

  // Request zero denotes an idle encoder, not a storage enable.
  always_comb begin
    if (request[3]) index = 2'd3;
    else if (request[2]) index = 2'd2;
    else if (request[1]) index = 2'd1;
    else if (request[0]) index = 2'd0;
  end

endmodule
