module response_decode (
  input  logic [3:0] request,
  output logic [2:0] response,
  output logic       matched
);
  always_comb begin
    response = 3'd0;
    matched = 1'b1;
    casez (request)
      4'b1???: response = 3'd4;
      4'b10??: response = 3'd6;
      4'b010?: response = 3'd2;
      4'b0011: response = 3'd3;
      default: begin
        response = 3'd0;
        matched = 1'b0;
      end
    endcase
  end
endmodule

module request_router (
  input  logic [3:0] request,
  output logic [2:0] response,
  output logic       matched
);
  response_decode u_decode (
    .request(request),
    .response(response),
    .matched(matched)
  );
endmodule

module top (
  input  logic [3:0] request,
  output logic [2:0] response,
  output logic       matched
);
  request_router u_router (
    .request(request),
    .response(response),
    .matched(matched)
  );
endmodule
